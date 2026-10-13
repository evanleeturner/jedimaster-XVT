"""Arrival: a craft coming in out of hyperspace or out of a mothership's hangar.

Purpose:
    The engine starts a round that comes out of hyperspace after the flight's
    start with the activity ARRIVING_FROM_HYPERSPACE, and a round out of a
    hangar that no player's group owns with LEAVING_HANGAR.

    Out of hyperspace this behavior flies the craft in, as the 1997 AI did,
    then sets its activity to NONE: from its next think the behavior of its
    order flies it. The engine placed the round 8 times 65,535 units back
    from its point, facing it. The craft is marked arriving (the engine then
    flies it straight on and leaves its speed alone) at speed 3,600, which
    drops to 1,800 after 6 of the game's seconds and to 900 after 9. A
    flight's leader ends its jump after 11, on time alone. A follower ends
    its jump with its leader's: at its first think after its leader
    (``CraftView.leader``) is no longer arriving, or is not in the question;
    after 11 seconds at the latest. Then the craft is active again at speed
    250, or 0 when its order starts stopped. The steps and times are the
    1997 AI's, at 236 ticks a game second.

    Out of a hangar the craft flies its order from its first think, at its
    own throttle, as the craft of a player's group do: ``leave_hangar`` ends
    the leaving mark on the order's decision. No speed or throttle is set.

Phases, the notebook's phase byte:

    START    not begun; the first think begins the jump
    JUMPING  out of hyperspace, slowing in steps

The scratch bytes, little-endian: u16 the think interval the craft had when
it began; the rest zero.

Invariants:
    - Times run from the tick the craft began, kept in the notebook.
    - It thinks every ARRIVAL_INTERVAL ticks while it jumps, and puts the
      craft's own interval back when it ends.
    - A craft never picks its leader: the engine names it, and names a new
      one when a leader is lost.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import notebook
from ..orders import OrderCode
from ..world import Actions
from ..world import Activity
from ..world import Arrival
from ..world import CraftView
from ..world import NO_OBJECT
from ..world import Question
from . import BehaviorCode
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.ARRIVAL
START = 0
JUMPING = 1

# The 1997 AI thought every 59 ticks while out of hyperspace.
ARRIVAL_INTERVAL = 59
GAME_SECOND = 236
# Speed out of hyperspace: 3,600 for the first 6 game seconds, 1,800 to the
# 9th, 900 to the 11th; then the craft has arrived.
JUMP_SPEED = 3600
SLOWER_SECOND = 6
SLOWER_SPEED = 1800
SLOWEST_SECOND = 9
SLOWEST_SPEED = 900
JUMP_TICKS = 11 * GAME_SECOND
ARRIVED_SPEED = 250
# The orders a craft starts stopped for, as the engine's
# mission_order_starts_stopped names them; out of hyperspace it stops too.
STOPPED_ORDERS = frozenset(
    {
        OrderCode.HOLD_STEADY,
        OrderCode.DISABLED,
        OrderCode.HOLD_STEADY_2,
        OrderCode.HOLD_STEADY_3,
        OrderCode.HOLD_STEADY_4,
        OrderCode.HOLD_STEADY_5,
        OrderCode.HOLD_STEADY_6,
        OrderCode.HOLD_STEADY_7,
        OrderCode.NULL,
    }
)

_SCRATCH = struct.Struct("<H")


def is_arriving(view: CraftView) -> bool:
    """Whether the engine has the craft arriving from hyperspace, so this
    behavior flies it."""
    return view.activity == Activity.ARRIVING_FROM_HYPERSPACE


def is_leaving_hangar(view: CraftView) -> bool:
    """Whether the engine has the craft leaving a hangar: its order flies it."""
    return view.activity == Activity.LEAVING_HANGAR


def jump_speed(ticks: int) -> int:
    """Return the speed out of hyperspace ticks after the jump began."""
    seconds = ticks // GAME_SECOND
    if seconds < SLOWER_SECOND:
        return JUMP_SPEED
    if seconds < SLOWEST_SECOND:
        return SLOWER_SPEED
    return SLOWEST_SPEED


def leader_arriving(view: CraftView, question: Question) -> bool:
    """Whether the craft's leader is in the question and still arriving."""
    return any(
        contact.object_index == view.leader
        and contact.activity == Activity.ARRIVING_FROM_HYPERSPACE
        for contact in question.contacts
    )


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step in from hyperspace."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    if book.phase == START:
        return _begin(view, book, tick)
    (interval,) = _SCRATCH.unpack_from(book.scratch)
    ticks = tick - book.phase_tick
    led = view.leader == NO_OBJECT or leader_arriving(view, question)
    if ticks < JUMP_TICKS and led:
        arrival = Arrival(arriving=True, speed=jump_speed(ticks))
        return Decision(book.encode(), Actions(arrival=arrival))
    speed = 0 if view.order_code in STOPPED_ORDERS else ARRIVED_SPEED
    actions = Actions(
        activity=Activity.NONE,
        think_interval=interval,
        arrival=Arrival(arriving=False, speed=speed),
    )
    _log_finished(view, ticks, interval)
    return Decision(book.encode(), actions)


def leave_hangar(view: CraftView, decision: Decision) -> Decision:
    """Return the order's decision for a craft leaving a hangar, with the
    leaving mark ended unless the order set an activity itself."""
    _log_finished(view, 0, view.think_interval)
    if decision.actions.activity is not None:
        return decision
    actions = dataclasses.replace(decision.actions, activity=Activity.NONE)
    return dataclasses.replace(decision, actions=actions)


def _begin(view: CraftView, book: notebook.Notebook, tick: int) -> Decision:
    scratch = _SCRATCH.pack(view.think_interval)
    scratch += bytes(notebook.SCRATCH_BYTES - _SCRATCH.size)
    actions = Actions(
        think_interval=ARRIVAL_INTERVAL,
        arrival=Arrival(arriving=True, speed=JUMP_SPEED),
    )
    logger.info(
        "arrival.started object=%d activity=%d ticks=%d",
        view.object_index,
        view.activity,
        JUMP_TICKS,
    )
    began = notebook.Notebook(CODE, JUMPING, book.dice, tick, scratch)
    return Decision(began.encode(), actions)


def _log_finished(view: CraftView, ticks: int, interval: int) -> None:
    logger.info(
        "arrival.finished object=%d activity=%d ticks=%d interval=%d leader=%d",
        view.object_index,
        view.activity,
        ticks,
        interval,
        view.leader,
    )
