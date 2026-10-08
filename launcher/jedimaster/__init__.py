"""jedimaster: read X-Wing vs. TIE Fighter and Balance of Power mission files.

Purpose:
    A dependency-free reader for the game's .tie mission files (format
    versions 12 and 14), a text renderer, a JSON export, and an install
    finder that resolves game paths the way the game does.

Flow:
    ``install`` finds an install and its missions; ``mission`` reads, renders
    and exports them; ``setup`` holds the lobby's setup document's fingerprint
    and engine values; ``icons`` reads the briefing map's icon sheets and
    draws its icons; ``text``, ``fonts`` and ``pictures`` read the menus'
    text files, fonts and pictures; ``__main__`` is the command line.

Invariants:
    - Standard library only at run time.

Call:
    ``python -m jedimaster dump <mission>``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

__version__ = "0.1.0"
