"""Wait and Wait 2 (orders 19 and 20): stop for the order's time.

Purpose:
    The behavior that flies both orders, apart from the player's wait
    command, which ``wait`` flies. The 1997 plan for both (waitpln) stopped
    the craft and set its maneuver timer to the order's variable1 times
    WAIT_UNIT ticks (``paiman_initwaitmaneuver``); the order was done once
    the timer ran out. Here the craft sets the timer with the
    ``maneuver_timer`` action on its first think of the order slot, the
    engine counts it down, and ``done`` reads it (``CraftView.
    maneuver_timer``). The craft stops where it is: throttle 0 and no
    steer, so a turn already begun runs on to its angle, as the player's
    wait command's stop does. Its activity is WAITING, which the HUD shows
    with the time left.

    The 1997 plan never fought back. Here a craft on Wait defends itself as
    one on the player's wait command does: it fights
    an enemy after it while it may fight back (``attack.fights_back``), else
    weaves while an enemy sits on its tail, and stops again once clear. The
    timer runs on meanwhile.

    ``ended`` gives the activity back, NONE, once another behavior flies the
    craft and names none.

The state, SLOT.size bytes at the start of the scratch: u8 the order slot
whose timer the craft set.

Invariants:
    - The timer is set once for each order slot: Wait then Wait 2 share one
      notebook, and the slot tells them apart.
    - While the craft fights, the notebook is attack's and the activity
      stays WAITING, which says the timer was set; the think its time is up
      gives activity NONE, so a Wait in the next slot sets its own.
    - Pure: the decision depends only on the view and the question.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import notebook
from ..world import Actions
from ..world import Activity
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_OBJECT
from ..world import Question
from . import attack
from . import BehaviorCode
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.WAITING
# Ticks per unit of the order's variable1, as 1997 set the timer: five of the
# flight code's simulated seconds.
WAIT_UNIT = 1180
SLOT = struct.Struct("<B")
STOPPED = 0


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's move: stopped for the order's time, or defending
    itself while the time runs."""
    started = _started(view)
    timer = None if started else view.order_variable1 * WAIT_UNIT
    if timer is not None:
        logger.info(
            "waiting.start object=%d slot=%d ticks=%d",
            view.object_index,
            view.order_slot,
            timer,
        )
    over = started and view.maneuver_timer == 0
    activity = Activity.NONE if over else Activity.WAITING
    chaser = attack.pursuer(view, question)
    fights = chaser is not None and attack.fights_back(view, question, chaser)
    chased = attack.tail_threat(view, question) is not None
    if not (fights or chased or attack.weaving(view)):
        book = notebook.open_for(view, CODE, question.tick)
        scratch = SLOT.pack(view.order_slot) + book.scratch[SLOT.size :]
        actions = Actions(
            throttle=STOPPED,
            activity=activity,
            target=NO_OBJECT,
            lasers=(HOLD_FIRE, HOLD_FIRE),
            maneuver_timer=timer,
        )
        return Decision(dataclasses.replace(book, scratch=scratch).encode(), actions)
    decision = attack.fight(view, question, [chaser] if fights else [])
    actions = dataclasses.replace(
        decision.actions, activity=activity, maneuver_timer=timer
    )
    if actions.steer is None:
        actions = dataclasses.replace(actions, throttle=STOPPED)
    return dataclasses.replace(decision, actions=actions)


def done(view: CraftView) -> bool:
    """Return whether the order's time is up: the timer the craft set for
    its order slot has run out."""
    return view.maneuver_timer == 0 and _started(view)


def ended(view: CraftView, decision: Decision) -> Decision:
    """Return the decision of a craft another behavior now flies: its
    activity NONE when it still showed WAITING and that behavior names none."""
    if view.activity != Activity.WAITING or decision.actions.activity is not None:
        return decision
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)


def _started(view: CraftView) -> bool:
    """Return whether the craft set the timer for its order slot: its own
    notebook names the slot or, while attack's notebook stands in for a
    fight, its activity is still WAITING."""
    try:
        book = notebook.decode(view.notebook)
    except notebook.NotebookError:
        book = None
    if book is not None and book.behavior == CODE:
        return SLOT.unpack_from(book.scratch)[0] == view.order_slot
    return view.activity == Activity.WAITING
