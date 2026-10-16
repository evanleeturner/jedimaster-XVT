"""The host's matchmaker: where the games of one network battle find each other.

Purpose:
    A network battle's games meet through an HTTPS directory before they
    link directly. The matchmaker is that directory, run by the host's own
    launcher on one port the host opens: it serves only the games the
    launcher issued, under secret ids, with a certificate made once per
    install and known by its fingerprint.

Flow:
    ``directory`` keeps the issued games, their rooms and the connection
    offers; ``server`` serves it over TLS in the directory protocol 2 that
    Aeron's client speaks; ``certificate`` makes and loads the pair and its
    fingerprint; ``cli`` runs it alone as ``python -m jedimaster matchmaker``.

Invariants:
    - Only ``server`` imports the web framework and only ``certificate`` the
      certificate library (the ``page`` and ``matchmaker`` extras); the
      directory and the command line load without them.
    - Nothing here starts a game or reaches Discord.

Call:
    ``python -m jedimaster matchmaker --games 1``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)
