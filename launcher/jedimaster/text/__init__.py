"""The text sub-package: the game's text files, as the game reads them.

Written from the game's files and notes describing how the game reads
them, checked against the game's own reading.

Purpose:
    Read the text files of X-Wing vs. TIE Fighter and Balance of Power the
    way the game reads them (``strings.txt``, ``fronttxt.txt``,
    ``specdesc.txt``, ``xvterr.txt``, ``joystick.txt``, ``credits.txt``),
    each byte kept, one line reader for all; refuse what the game stops on;
    print each in the answer sheets' format; export them as JSON.

Flow:
    ``lines`` reads lines for any buffer size; ``strings`` (with the table
    data of ``tables``), ``menus``, ``specs``, ``joystick`` and ``credits``
    read one kind each; ``game.text_view`` resolves and reads an install's
    six files; ``render`` prints; ``to_json`` exports; ``cli`` is the
    ``text`` command; ``sheet`` holds the sheets' quoting, shared with the
    fonts and pictures.

Invariants:
    - Standard library only.
    - The tables of ``tables`` are this sub-package's only game data.

Call:
    ``from jedimaster.text import text_view, render_text``
"""

from __future__ import annotations

import logging

from .credits import CreditsFile
from .credits import CreditsLine
from .credits import CreditsPage
from .credits import read_credits
from .game import read_kind
from .game import TEXT_FILES
from .game import text_view
from .game import TextFile
from .game import TextView
from .joystick import JoystickAction
from .joystick import JoystickFile
from .joystick import read_joystick
from .lines import Line
from .lines import LineBuffer
from .lines import LineStream
from .lines import read_lines
from .menus import error_message
from .menus import ErrorMessages
from .menus import front_text
from .menus import FrontText
from .menus import read_errors
from .menus import read_front
from .render import render_text
from .sheet import quote
from .specs import read_specs
from .specs import SpecEntry
from .specs import SpecsFile
from .strings import read_strings
from .strings import StringEntry
from .strings import StringsFile
from .strings import StringsFormatError
from .strings import StringsTable
from .to_json import text_schema
from .to_json import text_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "CreditsFile",
    "CreditsLine",
    "CreditsPage",
    "ErrorMessages",
    "FrontText",
    "JoystickAction",
    "JoystickFile",
    "Line",
    "LineBuffer",
    "LineStream",
    "SpecEntry",
    "SpecsFile",
    "StringEntry",
    "StringsFile",
    "StringsFormatError",
    "StringsTable",
    "TEXT_FILES",
    "TextFile",
    "TextView",
    "error_message",
    "front_text",
    "quote",
    "read_credits",
    "read_errors",
    "read_front",
    "read_joystick",
    "read_kind",
    "read_lines",
    "read_specs",
    "read_strings",
    "render_text",
    "text_schema",
    "text_to_json",
    "text_view",
]
