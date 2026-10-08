"""The pilot sub-package: a pilot's two files, and the record the game loads.

Written from the game's files and notes describing how the game loads a
pilot, checked against the game's own loading.

Purpose:
    Read a pilot's ``.pl2`` (the full record) and ``.plt`` (the base
    game's record) by the layout of the game's program; load a pilot from
    a folder the way the game does, merge included; print the layout and
    the records in the answer sheets' format; export the loaded record as
    JSON.

Flow:
    ``layout`` holds the structs; ``record`` reads bytes into records;
    ``defaults`` reads a new pilot's choices from an install; ``merge``
    merges a base record into a full one; ``load`` runs the game's load;
    ``render`` prints; ``to_json`` exports; ``cli`` is the ``pilot``
    command.

Invariants:
    - Standard library only.
    - The structs of ``layout`` are this sub-package's only game data.

Call:
    ``from jedimaster.pilot import install_defaults, load_pilot, render_load``
"""

from __future__ import annotations

import logging

from .defaults import Defaults
from .defaults import install_defaults
from .layout import BASE_RECORD
from .layout import FULL_RECORD
from .layout import Member
from .layout import Struct
from .layout import struct
from .layout import STRUCTS
from .load import Event
from .load import load_files
from .load import load_pilot
from .load import PilotLoad
from .merge import merge
from .merge import merge_plan
from .record import locate
from .record import PilotFormatError
from .record import read_pilot_file
from .record import read_record
from .record import Text
from .render import record_lines
from .render import render_layout
from .render import render_load
from .render import render_raw
from .to_json import pilot_schema
from .to_json import pilot_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "BASE_RECORD",
    "Defaults",
    "Event",
    "FULL_RECORD",
    "Member",
    "PilotFormatError",
    "PilotLoad",
    "STRUCTS",
    "Struct",
    "Text",
    "install_defaults",
    "load_files",
    "load_pilot",
    "locate",
    "merge",
    "merge_plan",
    "pilot_schema",
    "pilot_to_json",
    "read_pilot_file",
    "read_record",
    "record_lines",
    "render_layout",
    "render_load",
    "render_raw",
    "struct",
]
