"""Start the AI program: ``python3 ai/jedimaster_ai`` or ``python3 -m jedimaster_ai``.

Purpose:
    The engine starts the program by its folder, the way a launch
    description's ``ai_program`` names it. Python then puts the folder
    itself first on the import path, where the package's own modules would
    be found as top-level modules; the folder above it is put there
    instead, so the package imports as ``jedimaster_ai``.
"""

from __future__ import annotations

import os
import sys

if not __package__:
    sys.path[0] = os.path.dirname(os.path.abspath(sys.path[0]))

from jedimaster_ai.program import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
