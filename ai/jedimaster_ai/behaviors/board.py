"""The boarding orders: Give, Take, Exchange, Capture, Destroy, Pick Up, SS
Board and Board to Repair (orders 12 to 17, 31 and 32): find a craft to
board, fly to the point where the dock begins, and dock.

Purpose:
    A craft on a boarding order looks for a craft to board, flies to the
    point beside that craft's docking hatch that the engine names
    (``CraftView.board_point``), and there asks the engine to dock (the
    board action); the dock is the engine's. 1997's plans are the
    blueprint: the eight boardto plans stopped the craft and looked
    (``paiorder_lookforcrafttoboardorder``), board2pln flew the approach
    (the first phase of ``paiman_boardmaneuver``).

    The target is a player's command target first, when the question shows
    it, no other boarder keeps it (Shift+B names one) and the craft has not
    boarded it (each boarding counts a different craft); else
    the nearest craft its order's first pair of targets names that may be
    boarded now, and only when none may, the nearest of the rest its order
    names (1997's fallback to the second pair,
    ``old:pai.c:654-700``): by 1997's rough distance with no range limit,
    the earlier contact between two as near. A craft
    may be boarded now when its order holds it still (PARKED), it is a
    platform, or none of its subsystems work (DISABLED); for any order but
    Capture and Destroy also when it waits to be boarded (AWAITING, or a
    Rendezvous craft's line "Waiting for boarding craft") or is a boarder
    stopped to look (a looking activity), 1997's await-board and stop
    maneuvers. Never a craft another boarder keeps (``Contact.boarder``:
    of two boarders that chose one craft, the lower object index keeps it),
    one the craft has boarded (``CraftView.boarded``), or one breaking up or
    exploding. Only craft, never a static object or a point.

    It keeps its target, as 1997's approach did, until the question no
    longer shows it, its slot holds another craft, it breaks up or
    explodes, or another boarder keeps it; then it looks again.

    The approach: it steers at the board point at full throttle
    past 16,384 units, at half past 8,192, at a quarter past 2,048, and
    stops within 2,048 (1997 meant the same, but bug #47 left
    its full throttle overwritten by the half). The board point belongs to
    the target the craft named on its last think, so on the think it picks
    a target, and until the engine names the point, it steers at the
    target itself and measures from the edge of the target's clearance
    (``tactics.clearance``). At the point (AT_BOARD_POINT) it stops and
    asks the engine to dock with the target, every think until the dock
    begins.

    While a homing shot closes on it on the approach
    (``countermeasures.incoming``), it weaves about its way to the point
    (``weave``) at full throttle, each answer flagged ``weaving``, keeping
    its target, and flies the approach on once the weave ends; never at the
    point or while the engine docks it. 1997's approach plan, board3pln,
    answered hits too (AVOIDHITORDR).

    While the engine docks it (``CraftView.board_phase`` 1 to 3: align,
    transfer, separate) it sends no target, steer or throttle: the dock
    reads the craft's target every step, so a new one would be docked with.
    Once the dock ends it looks again where it is: a craft it boarded is in
    ``CraftView.boarded`` and is never picked
    again; after a dock the engine gave up (its target gone, wrecked or
    replaced, the craft disabled, a player's craft moving) it keeps its
    target while it may and approaches it again.

    Its status line: while it looks, the order's own, "Looking
    for craft to capture" for Capture, "... to pick up" for Pick Up, "...
    to board" for the others; "Docking" from the approach to the dock's
    end. Its cannons
    never fire; its turrets defend it (``turrets.defend``), and avoidance
    steers it round other craft and leaves its target to it.

    The goal (``goal_state``): the order is done (state 2) once the craft's
    boardings reach the order's variable2; with none of its targets left,
    in the battle or in a group still to arrive, its state is 3, 1997's
    "no boarding target".

Phases, the notebook's phase byte, 0 before the behavior first writes it:

    LOOKING   no target: stopped, the looking line
    APPROACH  steer at the board point, the throttle by the distance left;
              weave from a homing shot closing
    AT_POINT  at the board point, stopped, asking the engine to dock
    DOCKING   the engine docks the craft: nothing sent but the status line

The scratch bytes, little-endian: u8 the order slot the memory is for, u8 1
while a target is held, else 0, u16 the target's index and u16 its
signature, then the approach's weave (``weave.STATE_BYTES``); the rest
zero. A dock keeps them as they were.

Invariants:
    - Its cannons never fire.
    - It names no target the question does not show, and none while the
      engine docks it.
    - It asks to dock only with the target it held on its last think, at
      that target's board point.
    - A notebook written on another order slot starts looking again.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import geometry
from .. import notebook
from .. import tactics
from .. import weave
from ..world import Actions
from ..world import Activity
from ..world import Books
from ..world import Contact
from ..world import ContactFlags
from ..world import CraftView
from ..world import HOLD_FIRE
from ..world import NO_OBJECT
from ..world import Question
from ..world import Steer
from . import BehaviorCode
from . import countermeasures
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.BOARD
LOOKING = 1
APPROACH = 2
AT_POINT = 3
DOCKING = 4

# The boarding orders' codes (``orders.OrderCode``). orders.py imports this
# module, so the numbers are written here; tests/test_board.py holds them to
# OrderCode.
GIVE = 12
TAKE = 13
EXCHANGE = 14
CAPTURE = 15
DESTROY = 16
PICK_UP = 17
SS_BOARD = 31
REPAIR = 32
ORDERS = frozenset({GIVE, TAKE, EXCHANGE, CAPTURE, DESTROY, PICK_UP, SS_BOARD, REPAIR})
# The orders that board only a craft parked, a platform or disabled.
_STRICT_ORDERS = frozenset({CAPTURE, DESTROY})

# The order states goal_state gives: progress.DONE and
# progress.NO_BOARDING_TARGET (progress imports this module).
DONE = 2
NO_TARGET_LEFT = 3

# The approach's throttle by the distance left: full past
# FULL_RANGE, half past HALF_RANGE, a quarter past BOARD_RANGE, else stopped.
FULL_RANGE = 0x4000
HALF_RANGE = 0x2000
BOARD_RANGE = 2048
HALF_THROTTLE = 0x8000
QUARTER_THROTTLE = 0x4000

_LOOKING_LINES = {
    CAPTURE: Activity.LOOKING_TO_CAPTURE,
    PICK_UP: Activity.LOOKING_TO_PICK_UP,
}
_LOOKING_ACTIVITIES = frozenset(
    {
        Activity.LOOKING_TO_BOARD,
        Activity.LOOKING_TO_CAPTURE,
        Activity.LOOKING_TO_PICK_UP,
    }
)
_LIVE_KINDS = (tactics.OBJECT_KIND_ACTIVE, tactics.OBJECT_KIND_DISABLED)
_HOLD = (HOLD_FIRE, HOLD_FIRE)

_MEMORY = struct.Struct("<BBHH")


@dataclasses.dataclass(frozen=True)
class Memory:
    """The scratch bytes' fields."""

    slot: int = 0
    held: int = 0
    target: int = NO_OBJECT
    signature: int = 0
    weave: weave.Weave = weave.Weave()

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        return cls(
            *_MEMORY.unpack_from(scratch), weave.Weave.read(scratch[_MEMORY.size :])
        )

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = _MEMORY.pack(self.slot, self.held, self.target, self.signature)
        packed += self.weave.write()
        return packed + bytes(notebook.SCRATCH_BYTES - len(packed))


@dataclasses.dataclass(frozen=True)
class Search:
    """What a look found: the target, None for none, where it came from,
    and how many contacts the order names and how many of them may be
    boarded now."""

    target: Contact | None
    source: str
    named: int
    boardable: int


def think(view: CraftView, question: Question) -> Decision:
    """Return the craft's next move: nothing while the engine docks it, else
    its approach to the target it holds, else a new target's, else a stop to
    look."""
    book = notebook.open_for(view, CODE, question.tick)
    memory = Memory.read(book.scratch)
    if view.board_phase:
        return _docking(view, question, book, memory)
    if memory.slot != view.order_slot:
        memory = Memory(slot=view.order_slot)
    if book.phase == DOCKING:
        memory = _undocked(view, memory)
    if memory.held:
        target, why = _kept(view, question, memory)
        if target is not None:
            return _approach(view, question, book, memory, target, fresh=False)
        logger.info(
            'board.lost object=%d target=%d why="%s"',
            view.object_index,
            memory.target,
            why,
        )
        memory = Memory(slot=view.order_slot)
    search = look(view, question)
    if search.target is None:
        return _looking(view, question, book, memory, search)
    target = search.target
    logger.info(
        'board.target object=%d target=%d source="%s" distance=%d order=%d '
        "named=%d boardable=%d",
        view.object_index,
        target.object_index,
        search.source,
        rough_distance(*tactics.offset(view, target)),
        view.order_code,
        search.named,
        search.boardable,
    )
    memory = Memory(view.order_slot, 1, target.object_index, target.object_signature)
    return _approach(view, question, book, memory, target, fresh=True)


def look(view: CraftView, question: Question) -> Search:
    """Return the craft to board: the command target first, else the
    nearest the order's first pair names that may be boarded now, else the
    nearest of the rest the order names."""
    for contact in question.contacts:
        if (
            contact.object_index == view.command_target
            and contact.object_index != view.object_index
            and contact.object_kind in _LIVE_KINDS
            and _free(view, contact)
            and contact.object_signature not in view.boarded
        ):
            return Search(contact, "command", 0, 0)
    strict = view.order_code in _STRICT_ORDERS
    # The nearest of the first pair's, then of the rest's.
    nearest: list[Contact | None] = [None, None]
    reaches = [0, 0]
    named = boardable = 0
    for position, contact in enumerate(question.contacts):
        if not view.order_target_mask >> position & 1:
            continue
        if contact.object_index == view.object_index:
            continue
        named += 1
        if (
            contact.object_kind not in _LIVE_KINDS
            or not may_board(contact, strict)
            or not _free(view, contact)
            or contact.object_signature in view.boarded
        ):
            continue
        boardable += 1
        reach = rough_distance(*tactics.offset(view, contact))
        pair = 0 if view.order_first_pair_mask >> position & 1 else 1
        if nearest[pair] is None or reach < reaches[pair]:
            nearest[pair], reaches[pair] = contact, reach
    first, rest = nearest
    return Search(first if first is not None else rest, "search", named, boardable)


def may_board(contact: Contact, strict: bool) -> bool:
    """Return whether a contact may be boarded now, 1997's test: held still
    by its order, a platform, or disabled; unless strict (Capture and
    Destroy), also waiting to be boarded, on Disabled, Await Boarding or at
    a rendezvous point, or a boarder stopped to look."""
    if (
        contact.flags & (ContactFlags.PARKED | ContactFlags.DISABLED)
        or contact.genus == tactics.GENUS_PLATFORM
    ):
        return True
    if strict:
        return False
    return (
        bool(contact.flags & ContactFlags.AWAITING)
        or contact.activity == Activity.WAITING_FOR_BOARDING_CRAFT
        or contact.activity in _LOOKING_ACTIVITIES
    )


def rough_distance(dx: int, dy: int, dz: int) -> int:
    """Return 1997's rough distance of an offset
    (``flight_geometry_rough_distance_between_refs``): the larger of the x
    and y lengths plus half the smaller, then the same with the z length,
    in world units."""
    dx, dy, dz = abs(dx), abs(dy), abs(dz)
    across = dx + (dy >> 1) if dx > dy else dy + (dx >> 1)
    if across > dz:
        return across + (dz >> 1)
    return (across >> 1) + dz


def throttle_for(distance_left: int) -> int:
    """Return the approach's throttle for the distance left to the board
    point."""
    if distance_left > FULL_RANGE:
        return tactics.FULL_THROTTLE
    if distance_left > HALF_RANGE:
        return HALF_THROTTLE
    if distance_left > BOARD_RANGE:
        return QUARTER_THROTTLE
    return 0


def goal_state(view: CraftView) -> int:
    """Return the state 1997's boarding goal gives the craft's order slot:
    DONE once its boardings reach the order's variable2, NO_TARGET_LEFT
    with none of its targets left now or in a group still to arrive, else
    0."""
    if view.order_variable2 <= view.order_progress[view.order_slot]:
        return DONE
    if not view.books & Books.TARGETS_LEFT:
        return NO_TARGET_LEFT
    return 0


def looking_line(view: CraftView) -> Activity:
    """Return the status line the craft shows while it looks."""
    return _LOOKING_LINES.get(view.order_code, Activity.LOOKING_TO_BOARD)


def _kept(
    view: CraftView, question: Question, memory: Memory
) -> tuple[Contact | None, str]:
    """Return the target the craft holds, None when it must look again, and
    why it lost it."""
    for contact in question.contacts:
        if contact.object_index != memory.target:
            continue
        if contact.object_signature != memory.signature:
            return None, "replaced"
        if contact.object_kind not in _LIVE_KINDS:
            return None, "wrecked"
        if not _free(view, contact):
            return None, "taken"
        return contact, ""
    return None, "gone"


def _free(view: CraftView, contact: Contact) -> bool:
    """Return whether no other craft keeps the contact to board or carry."""
    return contact.boarder in (NO_OBJECT, view.object_index)


def _looking(
    view: CraftView,
    question: Question,
    book: notebook.Notebook,
    memory: Memory,
    search: Search,
) -> Decision:
    """Return the stop to look: no target, stopped, the looking line."""
    if book.phase != LOOKING:
        book = book.in_phase(LOOKING, question.tick)
        logger.debug(
            "board.looking object=%d order=%d named=%d boardable=%d",
            view.object_index,
            view.order_code,
            search.named,
            search.boardable,
        )
    actions = Actions(
        throttle=0,
        activity=_changed(view, looking_line(view)),
        target=NO_OBJECT,
        lasers=_HOLD,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _approach(
    view: CraftView,
    question: Question,
    book: notebook.Notebook,
    memory: Memory,
    target: Contact,
    fresh: bool,
) -> Decision:
    """Return the approach to the target's board point, stopped there and
    asking the engine to dock; on the way, a weave about the heading to the
    point while a homing shot closes on the craft."""
    pointed = not fresh and bool(view.books & Books.BOARD_POINT)
    if pointed:
        px, py, pz = view.board_point
        dx, dy, dz = px - view.x, py - view.y, pz - view.z
        left = geometry.distance(dx, dy, dz)
    else:
        dx, dy, dz = tactics.offset(view, target)
        left = max(0, geometry.distance(dx, dy, dz) - tactics.clearance(target))
    there = pointed and bool(view.books & Books.AT_BOARD_POINT)
    tick = question.tick
    flown, move, state = memory.weave, None, book.dice
    shot = None if there else countermeasures.incoming(view, question)
    if flown.flying:
        flown, move, state = weave.fly(view, flown, shot is not None, state, tick)
    elif shot is not None and weave.may_start(flown, tick):
        course = geometry.bearing(dx, dy, dz)[0]
        flown, move, state = weave.start(
            view, shot.object_index, flown, state, tick, course
        )
    memory = dataclasses.replace(memory, weave=flown)
    book = dataclasses.replace(book, dice=state)
    if move is not None:
        if book.phase != APPROACH:
            book = book.in_phase(APPROACH, tick)
        actions = Actions(
            steer=move.steer,
            throttle=tactics.FULL_THROTTLE,
            activity=_changed(view, Activity.BOARDING),
            target=target.object_index,
            lasers=_HOLD,
            countermeasure=move.flare,
            weaving=True,
        )
        scratch = memory.write()
        return Decision(dataclasses.replace(book, scratch=scratch).encode(), actions)
    throttle = 0 if there else throttle_for(left)
    phase = AT_POINT if there else APPROACH
    if book.phase != phase:
        book = book.in_phase(phase, question.tick)
        if there:
            logger.info(
                "board.at_point object=%d target=%d",
                view.object_index,
                target.object_index,
            )
    logger.debug(
        "board.approach object=%d target=%d left=%d throttle=%d pointed=%d there=%d",
        view.object_index,
        target.object_index,
        left,
        throttle,
        int(pointed),
        int(there),
    )
    steer = None
    if throttle:
        yaw, pitch = geometry.bearing(dx, dy, dz)
        steer = Steer(yaw, pitch, view.roll)
    dock = target.object_index if there else None
    if there:
        logger.debug(
            "board.dock object=%d target=%d order=%d",
            view.object_index,
            target.object_index,
            view.order_code,
        )
    actions = Actions(
        steer=steer,
        throttle=throttle,
        activity=_changed(view, Activity.BOARDING),
        target=target.object_index,
        lasers=_HOLD,
        board=dock,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _docking(
    view: CraftView, question: Question, book: notebook.Notebook, memory: Memory
) -> Decision:
    """Return the answer while the engine docks the craft: no target, steer
    or throttle (the dock reads the craft's target every step), "Docking"
    shown, cannons held, the memory kept for the dock's end."""
    if book.phase != DOCKING:
        book = book.in_phase(DOCKING, question.tick)
    logger.debug(
        "board.docking object=%d target=%d phase=%d",
        view.object_index,
        memory.target,
        view.board_phase,
    )
    actions = Actions(activity=_changed(view, Activity.BOARDING), lasers=_HOLD)
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _undocked(view: CraftView, memory: Memory) -> Memory:
    """Return the memory once the engine's dock has ended: forgotten when the
    craft boarded its target (its signature is in ``boarded``), so it looks
    again where it is; kept when the engine gave the dock up."""
    boarded = bool(memory.held) and memory.signature in view.boarded
    logger.info(
        "board.undocked object=%d target=%d boarded=%d",
        view.object_index,
        memory.target,
        int(boarded),
    )
    return Memory(slot=view.order_slot) if boarded else memory


def _changed(view: CraftView, activity: Activity) -> Activity | None:
    """Return the activity to send: None when the craft shows it already."""
    return None if view.activity == activity else activity
