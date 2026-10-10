"""The briefing sub-package: what the page's briefing player is given.

Purpose:
    Build, from an install and one mission file, the bundle the briefing
    player draws and plays from, and serve the fixed set of pictures and
    sounds it uses. The player itself is TypeScript (``launcher/page``).

Flow:
    ``find.mission_file`` turns a menu entry into a file; ``build`` reads
    the mission and the install's files into a bundle; ``art`` builds the
    pictures and sounds; ``schema`` describes the bundle; ``cli`` is the
    ``briefing`` command.

Invariants:
    - Readers are the existing sub-packages'; nothing here parses a game
      file itself.
    - ``schema`` is imported by name, not here: the page package imports
      ``model`` and owns the schema walker.

Call:
    ``from jedimaster.briefing import build_bundle``
"""

from __future__ import annotations

import logging

from .art import ART_NAMES
from .art import ArtStore
from .build import BriefingBuildError
from .build import build_bundle
from .find import mission_file

logger = logging.getLogger(__name__)

__all__ = [
    "ART_NAMES",
    "ArtStore",
    "BriefingBuildError",
    "build_bundle",
    "mission_file",
]
