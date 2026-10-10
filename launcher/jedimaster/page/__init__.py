"""The launcher's local page: a web page served on this computer.

Purpose:
    Serve one web page on the player's own computer and let it control the
    launcher over a WebSocket, through one fixed list of commands. The list
    (``protocol``, ``control``) does not depend on the web server, so a
    second user of the launcher, such as a chat bot, can call the same
    commands.

Flow:
    ``cli`` parses ``python -m jedimaster page`` and starts ``server``;
    ``server`` checks the host, the secret and the origin and hands each
    text message to ``control``; ``control`` runs one command from the list
    and answers; ``settings`` keeps the one settings file; ``schema`` builds
    the JSON Schema every message satisfies from the dataclasses of
    ``protocol``.

Invariants:
    - Only ``server`` imports the web framework (the optional ``page``
      extra); the readers and the other commands work without it.
    - The server binds to 127.0.0.1 and nothing else.

Call:
    ``python -m jedimaster page --install <folder>``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)
