"""Drift: the behavior of a craft whose order has no behavior written yet.

Purpose:
    Decide nothing. The craft's notebook goes back as it came and no action
    is sent, so the engine leaves the craft's steering and throttle as they
    are: a craft keeps the speed and heading it has. Every order uses this
    until a behavior for it is written.
"""

from __future__ import annotations

from ..world import CraftView
from ..world import Question
from . import Decision


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's own notebook and no action."""
    return Decision(view.notebook)
