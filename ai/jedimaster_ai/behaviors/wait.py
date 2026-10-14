"""Wait: a craft a player told to wait stops until it is told to go.

Purpose:
    Shift+W marks a computer craft waiting (``Commands.WAIT``); a go
    command, an evade command or an attack or cover order ends the mark.
    While it stands, the craft stops where it is, keeps its order for later
    and shows the activity WAITING_FOR_GO, which the HUD reads as awaiting
    further orders. A waiting craft defends itself as a craft going home
    does (rulings 33, 34 and 43): it fights an enemy that is after it while
    it may fight back (``attack.fights_back``), else weaves while an enemy
    sits on its tail, and stops again once clear. The 1997 wait plans
    (craftwaitforgopln, starshipwaitforgopln) stopped the craft and answered
    an attacker the same way, by their under-attack order.

    ``ended`` gives a craft the wait released its activity back: NONE,
    unless its behavior names another.

Invariants:
    - A waiting craft no enemy troubles leaves its notebook as it was, so
      its order's behavior finds its own memory when the wait ends.
    - Its order slot never moves while it waits.
"""

from __future__ import annotations

import dataclasses
import logging

from ..world import Actions
from ..world import Activity
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_OBJECT
from ..world import Question
from . import attack
from . import Decision

logger = logging.getLogger(__name__)

STOPPED = 0


def think(view: CraftView, question: Question) -> Decision:
    """Return a waiting craft's move: stopped, or defending itself."""
    if view.activity != Activity.WAITING_FOR_GO:
        logger.info("wait.start object=%d", view.object_index)
    chaser = attack.pursuer(view, question)
    fights = chaser is not None and attack.fights_back(view, question, chaser)
    chased = attack.tail_threat(view, question) is not None
    if not (fights or chased or attack.weaving(view)):
        actions = Actions(
            throttle=STOPPED,
            activity=Activity.WAITING_FOR_GO,
            target=NO_OBJECT,
            lasers=(HOLD_FIRE, HOLD_FIRE),
        )
        return Decision(view.notebook, actions)
    decision = attack.fight(view, question, [chaser] if fights else [])
    actions = dataclasses.replace(decision.actions, activity=Activity.WAITING_FOR_GO)
    if actions.steer is None:
        actions = dataclasses.replace(actions, throttle=STOPPED)
    return dataclasses.replace(decision, actions=actions)


def ended(view: CraftView, decision: Decision) -> Decision:
    """Return the decision of a craft no longer waiting: its activity NONE
    when it still showed WAITING_FOR_GO and its behavior names none."""
    if (
        view.activity != Activity.WAITING_FOR_GO
        or decision.actions.activity is not None
    ):
        return decision
    logger.info("wait.end object=%d", view.object_index)
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)
