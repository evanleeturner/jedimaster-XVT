"""Arrival: a craft coming in out of hyperspace or out of a mothership's hangar.

Purpose:
    The engine starts a round that comes out of hyperspace after the flight's
    start with the activity ARRIVING_FROM_HYPERSPACE, and a round out of a
    hangar with LEAVING_HANGAR. This behavior flies such a craft in, as the
    1997 AI did, then sets its activity to NONE: from its next think the
    behavior of its order flies it.

    Out of hyperspace: the engine placed the round 8 times 65,535 units back
    from its point, facing it. The craft is marked arriving (the engine then
    flies it straight on and leaves its speed alone) at speed 3,600, which
    drops to 1,800 after 6 of the game's seconds and to 900 after 9. After
    11 it is active again at speed 250, or 0 when its order starts stopped.
    The steps and times are the 1997 AI's, at 236 ticks a game second.

    Out of a hangar: the craft keeps the heading it was placed with, from the
    hangar's inside point toward its outside one, at full throttle for 2,360
    ticks, the 1997 AI's time; then its own throttle is put back.

Phases, the notebook's phase byte:

    START    not begun; the first think begins by the craft's activity
    JUMPING  out of hyperspace, slowing in steps
    LEAVING  flying out of the hangar

The scratch bytes, little-endian: u16 the think interval and u16 the
throttle the craft had when it began; the rest zero.

Invariants:
    - Times run from the tick the craft began, kept in the notebook.
    - It thinks every ARRIVAL_INTERVAL ticks while it flies in, and puts the
      craft's own interval back when it ends.
    - Each craft ends on its own time: the 1997 AI's followers ended with
      their leader, and a round's craft begin within one think of each other.
"""

from __future__ import annotations

import logging
import struct

from .. import notebook
from ..orders import OrderCode
from ..world import Actions
from ..world import Activity
from ..world import Arrival
from ..world import CraftView
from ..world import Question
from . import BehaviorCode
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.ARRIVAL
START = 0
JUMPING = 1
LEAVING = 2

# The activities the engine starts an arriving craft with.
ACTIVITIES = frozenset({Activity.ARRIVING_FROM_HYPERSPACE, Activity.LEAVING_HANGAR})
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
HANGAR_TICKS = 2360
FULL_THROTTLE = 0xFFFF
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

_SCRATCH = struct.Struct("<HH")


def is_arriving(view: CraftView) -> bool:
    """Whether the engine has the craft arriving, so this behavior flies it."""
    return view.activity in ACTIVITIES


def jump_speed(ticks: int) -> int:
    """Return the speed out of hyperspace ticks after the jump began."""
    seconds = ticks // GAME_SECOND
    if seconds < SLOWER_SECOND:
        return JUMP_SPEED
    if seconds < SLOWEST_SECOND:
        return SLOWER_SPEED
    return SLOWEST_SPEED


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next step in from hyperspace or out of the hangar."""
    tick = question.tick
    book = notebook.open_for(view, CODE, tick)
    if book.phase == START:
        return _begin(view, book, tick)
    interval, throttle = _SCRATCH.unpack_from(book.scratch)
    ticks = tick - book.phase_tick
    if book.phase == JUMPING:
        if ticks < JUMP_TICKS:
            arrival = Arrival(arriving=True, speed=jump_speed(ticks))
            return Decision(book.encode(), Actions(arrival=arrival))
        speed = 0 if view.order_code in STOPPED_ORDERS else ARRIVED_SPEED
        actions = Actions(
            activity=Activity.NONE,
            think_interval=interval,
            arrival=Arrival(arriving=False, speed=speed),
        )
    else:
        if ticks < HANGAR_TICKS:
            return Decision(book.encode())
        actions = Actions(
            throttle=throttle, activity=Activity.NONE, think_interval=interval
        )
    logger.info(
        "arrival.finished object=%d activity=%d ticks=%d interval=%d",
        view.object_index,
        view.activity,
        ticks,
        interval,
    )
    return Decision(book.encode(), actions)


def _begin(view: CraftView, book: notebook.Notebook, tick: int) -> Decision:
    scratch = _SCRATCH.pack(view.think_interval, view.throttle)
    scratch += bytes(notebook.SCRATCH_BYTES - _SCRATCH.size)
    if view.activity == Activity.ARRIVING_FROM_HYPERSPACE:
        phase = JUMPING
        ticks = JUMP_TICKS
        actions = Actions(
            think_interval=ARRIVAL_INTERVAL,
            arrival=Arrival(arriving=True, speed=JUMP_SPEED),
        )
    else:
        phase = LEAVING
        ticks = HANGAR_TICKS
        actions = Actions(throttle=FULL_THROTTLE, think_interval=ARRIVAL_INTERVAL)
    logger.info(
        "arrival.started object=%d activity=%d ticks=%d",
        view.object_index,
        view.activity,
        ticks,
    )
    began = notebook.Notebook(CODE, phase, book.dice, tick, scratch)
    return Decision(began.encode(), actions)
