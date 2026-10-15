"""Wait and Wait 2 (orders 19 and 20): stop for the order's time.

Purpose:
    The behavior that flies both orders, apart from the player's wait
    command, which ``wait`` flies. Not written yet: ``think`` is drift's,
    so the craft keeps its speed and heading, and ``done`` never says the
    time is up.
"""

from __future__ import annotations

from ..world import CraftView
from . import drift

think = drift.think


def done(view: CraftView) -> bool:
    """Return whether the order's time is up: never, until it is written."""
    return False
