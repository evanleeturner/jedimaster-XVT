"""The behaviors: what a craft does for each kind of order.

Purpose:
    A behavior decides for one craft at a time. It is a function from the
    craft's view and the question (its tick and every contact) to a
    Decision: the craft's new notebook and its actions. ``orders`` picks the
    behavior from the craft's order code; ``avoidance.steer_clear`` then
    turns its steering clear of bulky craft in the way and
    ``turrets.defend`` aims the craft's turrets, whatever the order.

Invariants:
    - A behavior is pure: its decision depends only on the view and the
      question. It keeps nothing between calls; what it must remember goes in
      the notebook (``notebook.open_for``), with its own behavior code.
    - A behavior's code, the notebook's behavior byte, is 1 to 255 and
      belongs to that behavior alone: ``BehaviorCode`` holds them all.
"""

from __future__ import annotations

import dataclasses
import enum
import typing as t

from ..world import Actions
from ..world import CraftView
from ..world import NO_ACTIONS
from ..world import Question


class BehaviorCode(enum.IntEnum):
    """Each behavior's code in the notebooks it writes."""

    ATTACK = 1
    ARRIVAL = 2
    HOME = 3
    PATROL = 4


@dataclasses.dataclass(frozen=True)
class Decision:
    """One craft's decision: its new notebook bytes and its actions."""

    notebook: bytes
    actions: Actions = NO_ACTIONS


Behavior = t.Callable[[CraftView, Question], Decision]
