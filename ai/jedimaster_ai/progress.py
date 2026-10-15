"""Progress: a craft's way through its orders, to the next one or home.

Purpose:
    The engine keeps the mission's books by the 1997 rules and sends them in
    the view (ruling 26); this module decides what a craft does about them,
    each think, before a behavior flies it: whether it goes home
    (``behaviors.home`` flies it there) and whether its order slot moves. The
    rules are the 1997 leader plans' order conditions 38 to 40, in their
    order:

    - home once its books say aborted, departure due or orders done
    - an order whose goal is met (GOALS) marks its slot done, state 2; then
      home, when the craft can move, once every order its group has in slots
      0 to 2 is done (2 or 3), or once it skipped to order 4
    - else to slot 3 while its skip-to-order-4 trigger holds
    - else to the next slot, when its done slot is below 2 and the next slot
      holds an order

Invariants:
    - Only a craft that leads or flies alone moves its slot: a follower takes
      its leader's from the engine and goes home by its books alone.
    - Every move is one the engine's reader allows: the slot the craft is on,
      a slot 0 to 2 holding an order when it has not skipped, or slot 3 while
      the skip is due (``wire.check_shown``).
    - Only an order with a goal test in GOALS is ever done. Which orders keep
      the departure, abort and skip books at all is the engine's to say.
    - Pure: the decision depends only on the view. A move is logged at DEBUG
      when it is decided; going home is logged by ``behaviors.home``.
"""

from __future__ import annotations

import dataclasses
import logging
import types
import typing as t

from .behaviors import Decision
from .behaviors import patrol
from .behaviors import waiting
from .orders import OrderCode
from .world import Books
from .world import CraftView
from .world import NO_OBJECT
from .world import OrderMove

logger = logging.getLogger(__name__)

# An order slot's completion states: its goal met, and a boarding order with
# no target left.
DONE = 2
NO_BOARDING_TARGET = 3
DONE_STATES = (DONE, NO_BOARDING_TARGET)
# A group's orders sit in slots 0 to 2; only the skip reaches slot 3.
ORDER_SLOTS = 3
SKIP_SLOT = 3
# The books that send a craft home whatever its order.
HOME_BOOKS = Books.ABORTED | Books.DEPARTURE_DUE | Books.ORDERS_DONE


def _no_target_left(view: CraftView) -> bool:
    return not view.books & Books.TARGETS_LEFT


def _all_returned(view: CraftView) -> bool:
    return bool(view.books & Books.ALL_RETURNED)


# Each order's goal test, True once the goal is met, by the 1997 leader
# plans. Attack, Protect, Escort, Disable, SS Protect, SS Protect 2, SS Patrol
# and Attack and SS Patrol and Disable: no target left, now or in a group
# still to arrive, by the engine's 1997 count. Circle, Circle and Evade and SS
# Patrol Loop: their rounds reached the order's variable1. Wait and Wait 2:
# the order's time is up. SS Await Return: every group that goes home into
# the craft's group is back.
GOALS: t.Mapping[int, t.Callable[[CraftView], bool]] = types.MappingProxyType(
    {
        OrderCode.ATTACK: _no_target_left,
        OrderCode.PROTECT: _no_target_left,
        OrderCode.ESCORT: _no_target_left,
        OrderCode.DISABLE: _no_target_left,
        OrderCode.SS_PROTECT: _no_target_left,
        OrderCode.SS_PROTECT_2: _no_target_left,
        OrderCode.SS_PATROL_AND_ATTACK: _no_target_left,
        OrderCode.SS_PATROL_AND_DISABLE: _no_target_left,
        OrderCode.CIRCLE: patrol.rounds_done,
        OrderCode.CIRCLE_AND_EVADE: patrol.rounds_done,
        OrderCode.SS_PATROL_LOOP: patrol.rounds_done,
        OrderCode.WAIT: waiting.done,
        OrderCode.WAIT_2: waiting.done,
        OrderCode.SS_AWAIT_RETURN: _all_returned,
    }
)


@dataclasses.dataclass(frozen=True)
class Progress:
    """What a craft does about its books this think: whether it goes home,
    and the order move to send, None for none."""

    home: bool = False
    move: OrderMove | None = None


STAY = Progress()


def decide(view: CraftView) -> Progress:
    """Return whether the craft goes home and how its order slot moves."""
    if view.books & HOME_BOOKS:
        return Progress(home=True)
    if view.leader != NO_OBJECT:
        return STAY
    slot = view.order_slot
    state = view.order_states[slot]
    mark = 0
    goal = GOALS.get(view.order_code)
    if state not in DONE_STATES and goal is not None and goal(view):
        state = mark = DONE
    if (
        state in DONE_STATES
        and view.full_speed != 0
        and (view.books & Books.SKIPPED or _all_done(view, slot, state))
    ):
        move = OrderMove(slot, mark) if mark else None
        return _logged(view, Progress(home=True, move=move), "home")
    if view.books & Books.SKIP_DUE:
        return _logged(view, Progress(move=OrderMove(SKIP_SLOT, mark)), "skip")
    if state == DONE and slot < ORDER_SLOTS - 1 and view.order_codes[slot + 1]:
        return _logged(view, Progress(move=OrderMove(slot + 1, mark)), "next")
    if mark:
        return _logged(view, Progress(move=OrderMove(slot, mark)), "done")
    return STAY


def with_move(decision: Decision, move: OrderMove | None) -> Decision:
    """Return the decision with the order move added, unchanged for None."""
    if move is None:
        return decision
    actions = dataclasses.replace(decision.actions, order=move)
    return dataclasses.replace(decision, actions=actions)


def _all_done(view: CraftView, slot: int, state: int) -> bool:
    """Whether every order the craft's group has in slots 0 to 2 is done, its
    own slot in state."""
    states = list(view.order_states)
    states[slot] = state
    return all(
        states[order_slot] in DONE_STATES
        for order_slot in range(ORDER_SLOTS)
        if view.order_codes[order_slot]
    )


def _logged(view: CraftView, progress: Progress, why: str) -> Progress:
    move = progress.move
    if move is not None:
        logger.debug(
            'progress.move object=%d from=%d to=%d state=%d why="%s"',
            view.object_index,
            view.order_slot,
            move.slot,
            move.state,
            why,
        )
    return progress
