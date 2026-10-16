"""The boarding orders: Give, Take, Exchange, Capture, Destroy, Pick Up, SS
Board and Board to Repair (orders 12 to 17, 31 and 32): find a craft to
board and fly to the point where the dock begins.

Purpose:
    A craft on a boarding order looks for a craft to board, flies to the
    point beside that craft's docking hatch that the engine names
    (``CraftView.board_point``), and waits there for the dock, which is the
    engine's. 1997's plans are the blueprint: the eight boardto plans
    stopped the craft and looked (``paiorder_lookforcrafttoboardorder``),
    board2pln flew the approach (the first phase of
    ``paiman_boardmaneuver``).

    The target is a player's command target first, when the question shows
    it and no other boarder keeps it (Shift+B names one); else the nearest
    craft its order names that may be boarded now, by 1997's rough distance
    with no range limit, the earlier contact between two as near. A craft
    may be boarded now when its order holds it still (PARKED), it is a
    platform, or none of its subsystems work (DISABLED); for any order but
    Capture and Destroy also when it waits to be boarded (AWAITING) or is a
    boarder stopped to look (a looking activity), 1997's await-board and
    stop maneuvers. Never a craft another boarder keeps (``Contact.boarder``:
    of two boarders that chose one craft, the lower object index keeps it,
    ruling C9), one the craft has boarded (``CraftView.boarded``), or one
    breaking up or exploding. Only craft, never a static object or a point
    (ruling C6).

    It keeps its target, as 1997's approach did, until the question no
    longer shows it, its slot holds another craft, it breaks up or
    explodes, or another boarder keeps it; then it looks again.

    The approach (ruling C1): it steers at the board point at full throttle
    past 16,384 units, at half past 8,192, at a quarter past 2,048, and
    stops within 2,048 (ruling C2; 1997 meant the same, but bug #47 left
    its full throttle overwritten by the half). The board point belongs to
    the target the craft named on its last think, so on the think it picks
    a target, and until the engine names the point, it steers at the
    target itself and measures from the edge of the target's clearance
    (``tactics.clearance``). At the point (AT_BOARD_POINT) it waits,
    stopped.

    Its status line (ruling C8): while it looks, the order's own, "Looking
    for craft to capture" for Capture, "... to pick up" for Pick Up, "...
    to board" for the others; "Docking" from the approach on. Its cannons
    never fire; its turrets defend it (``turrets.defend``), and avoidance
    steers it round other craft and leaves its target to it.

    The goal (``goal_state``): the order is done (state 2) once the craft's
    boardings reach the order's variable2; with none of its targets left,
    in the battle or in a group still to arrive, its state is 3, 1997's
    "no boarding target".

Phases, the notebook's phase byte, 0 before the behavior first writes it:

    LOOKING   no target: stopped, the looking line
    APPROACH  steer at the board point, the throttle by the distance left
    AT_POINT  at the board point, stopped, waiting for the dock

The scratch bytes, little-endian: u8 the order slot the memory is for, u8 1
while a target is held, else 0, u16 the target's index and u16 its
signature; the rest zero.

Invariants:
    - Its cannons never fire.
    - It names no target the question does not show.
    - A notebook written on another order slot starts looking again.
"""

from __future__ import annotations

import dataclasses
import logging
import struct

from .. import geometry
from .. import notebook
from .. import tactics
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
from . import Decision

logger = logging.getLogger(__name__)

CODE = BehaviorCode.BOARD
LOOKING = 1
APPROACH = 2
AT_POINT = 3

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

# The approach's throttle by the distance left (ruling C2): full past
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

    @classmethod
    def read(cls, scratch: bytes) -> Memory:
        """Return the memory scratch bytes hold."""
        return cls(*_MEMORY.unpack_from(scratch))

    def write(self) -> bytes:
        """Return the memory as scratch bytes."""
        packed = _MEMORY.pack(self.slot, self.held, self.target, self.signature)
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
    """Return the craft's next move: its approach to the target it holds,
    else a new target's, else a stop to look."""
    book = notebook.open_for(view, CODE, question.tick)
    memory = Memory.read(book.scratch)
    if memory.slot != view.order_slot:
        memory = Memory(slot=view.order_slot)
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
    nearest the order names that may be boarded now."""
    for contact in question.contacts:
        if (
            contact.object_index == view.command_target
            and contact.object_index != view.object_index
            and contact.object_kind in _LIVE_KINDS
            and _free(view, contact)
        ):
            return Search(contact, "command", 0, 0)
    strict = view.order_code in _STRICT_ORDERS
    nearest = None
    nearest_reach = 0
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
        if nearest is None or reach < nearest_reach:
            nearest, nearest_reach = contact, reach
    return Search(nearest, "search", named, boardable)


def may_board(contact: Contact, strict: bool) -> bool:
    """Return whether a contact may be boarded now, 1997's test: held still
    by its order, a platform, or disabled; unless strict (Capture and
    Destroy), also waiting to be boarded or a boarder stopped to look."""
    if (
        contact.flags & (ContactFlags.PARKED | ContactFlags.DISABLED)
        or contact.genus == tactics.GENUS_PLATFORM
    ):
        return True
    if strict:
        return False
    return bool(contact.flags & ContactFlags.AWAITING) or (
        contact.activity in _LOOKING_ACTIVITIES
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
    point (ruling C2)."""
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
    """Return the status line the craft shows while it looks (ruling C8)."""
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
    """Return the approach to the target's board point, stopped there."""
    pointed = not fresh and bool(view.books & Books.BOARD_POINT)
    if pointed:
        px, py, pz = view.board_point
        dx, dy, dz = px - view.x, py - view.y, pz - view.z
        left = geometry.distance(dx, dy, dz)
    else:
        dx, dy, dz = tactics.offset(view, target)
        left = max(0, geometry.distance(dx, dy, dz) - tactics.clearance(target))
    there = pointed and bool(view.books & Books.AT_BOARD_POINT)
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
    actions = Actions(
        steer=steer,
        throttle=throttle,
        activity=_changed(view, Activity.BOARDING),
        target=target.object_index,
        lasers=_HOLD,
    )
    return Decision(dataclasses.replace(book, scratch=memory.write()).encode(), actions)


def _changed(view: CraftView, activity: Activity) -> Activity | None:
    """Return the activity to send: None when the craft shows it already."""
    return None if view.activity == activity else activity
