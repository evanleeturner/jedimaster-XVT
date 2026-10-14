"""Hold: the Hold Steady order and its six copies.

Purpose:
    The 1997 rule (ruling 28): throttle 0 and nothing more. Its 1997 plan
    had no condition, so the craft never moves to a later order and never
    goes home by its own books (the engine keeps none for it); its turrets
    defend, as every craft's do.
"""

from __future__ import annotations

from ..world import Actions
from ..world import CraftView
from ..world import Question
from . import Decision

HOLD = Actions(throttle=0)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's own notebook and throttle 0."""
    return Decision(view.notebook, HOLD)
