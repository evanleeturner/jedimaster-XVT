"""The launch sub-package: start one single-player battle in the engine.

Purpose:
    Describe a battle in a JSON file the engine's ``--launch`` option reads,
    start the engine, wait, and read the result file the engine writes. Two
    schemas fix the two documents: ``schema/launch.schema.json`` for the
    description and ``schema/result.schema.json`` for the result.

Flow:
    ``model`` holds the words both documents use and builds a description;
    ``schema`` builds the two JSON Schemas from those words; ``check``
    validates a document against a schema; ``run`` writes the description,
    starts the engine and reads the result; ``cli`` is the ``launch`` command.

Invariants:
    - Standard library only.
    - Only single player: the description's mode is the one word
      ``single_player``.

Call:
    ``python -m jedimaster launch --type training --side rebel --mission 1ta02bz.tie --pilot Ace --engine OpenXvT``
"""

from __future__ import annotations

import logging

from .model import build_description
from .model import GAME_TYPES

logger = logging.getLogger(__name__)

__all__ = ["GAME_TYPES", "build_description"]
