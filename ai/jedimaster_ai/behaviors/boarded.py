"""Boarded: a craft that waits to be boarded, on Disabled or Await Boarding.

Purpose:
    Both orders' 1997 plans, ``disabledpln`` and ``waitforboardpln``, flew
    one maneuver, the stop (``old:paiman.c``
    ``paiman_awaitboardmaneuver``): no roll, pitch or turn and throttle 0,
    every think, and they named no target of their own. Here, likewise:
    throttle 0 every think, and the order's status line, the one 1997 showed
    for its plan: "Awaiting boarding" (activity DISABLED) on Disabled,
    "Holding station" (HOLDING_STATION) on Await Boarding.

    The rest of both plans is the engine's or ``progress``'s:

    - the engine turns the craft's systems off, counts the boardings it is
      given in ``order_progress`` and turns the systems back on once they
      reach the order's variable1; until the engine counts them the count
      stays 0
    - ``done`` is the goal test ``progress`` reads, 1997's: the count has
      reached variable1, so a variable1 of 0 is done at once (2 retail
      Disabled slots); then the craft moves on or goes home as any order's
    - only Await Boarding goes home by its abort and departure books, as
      Balance of Power's waitforboardpln did, where every retail Await
      Boarding slot is; which orders keep those books is the engine's to say
      (``progress``)

    ``ended`` gives the status line back, NONE, once another behavior flies
    the craft and names none.

Invariants:
    - The notebook goes back as it came; no target, steer or cannon burst is
      sent, so a target the craft held stays, as 1997's plans left it.
    - Its turrets defend and a starfighter answers homing shots, as every
      craft's do; a craft whose systems are off fires no
      turret and launches no countermeasure, by the engine's own checks.
"""

from __future__ import annotations

import dataclasses
import types
import typing as t

from ..world import Actions
from ..world import Activity
from ..world import CraftView
from ..world import Question
from . import Decision

# The two orders' codes (``orders.OrderCode``). orders.py imports this module,
# so the numbers are written here; tests/test_boarded.py holds them to
# OrderCode.
DISABLED = 5
AWAIT_BOARDING = 6
# Each order's status line, by its 1997 plan.
LINES: t.Mapping[int, Activity] = types.MappingProxyType(
    {
        DISABLED: Activity.DISABLED,
        AWAIT_BOARDING: Activity.HOLDING_STATION,
    }
)
ORDERS = frozenset(LINES)


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's own notebook, throttle 0 and its order's status
    line."""
    return Decision(view.notebook, Actions(throttle=0, activity=LINES[view.order_code]))


def done(view: CraftView) -> bool:
    """Whether the boardings counted for the craft's order slot have reached
    the order's variable1."""
    return view.order_progress[view.order_slot] >= view.order_variable1


def ended(view: CraftView, decision: Decision) -> Decision:
    """Return the decision of a craft another behavior now flies: its
    activity NONE when it still showed a waiting line and that behavior names
    none."""
    if view.activity not in LINES.values() or decision.actions.activity is not None:
        return decision
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)
